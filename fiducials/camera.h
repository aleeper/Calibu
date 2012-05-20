#ifndef CAMERA_H
#define CAMERA_H

#include <array>
#include <Eigen/Dense>

inline Eigen::Vector2d project(const Eigen::Vector3d& p)
{
    return Eigen::Vector2d(p(0)/p(2), p(1)/p(2) );
}

inline Eigen::Vector3d project(const Eigen::Vector4d& p)
{
    return Eigen::Vector3d(p(0)/p(3), p(1)/p(3), p(2)/p(3) );
}

inline Eigen::Vector3d unproject(const Eigen::Vector2d& p)
{
    return Eigen::Vector3d( p(0), p(1), 1 );
}

inline Eigen::Vector4d unproject(const Eigen::Vector3d& p)
{
    return Eigen::Vector4d(p(0), p(1), p(2), 1 );
}

class AbstractCamera
{
public:
  AbstractCamera(int width, int height)
  {
      size[0] = width;
      size[1] = height;
  }

  //applies camera intrinsics including distortion
  virtual Eigen::Vector2d map(const Eigen::Vector2d& camframe) const = 0;

  //undo camera intrinsics including undistortion
  virtual Eigen::Vector2d unmap(const Eigen::Vector2d& imframe) const = 0;

  // Project p_cam in to the camera, applying intrinsics and distortion
  inline Eigen::Vector2d project_map(const Eigen::Vector3d& p_cam) const
  {
    return map( project(p_cam) );
  }

  // Take image coordinates into 3D camera coordinates on image plane
  inline Eigen::Vector3d unmap_unproject(const Eigen::Vector2d& img) const
  {
    return unproject( unmap( img) );
  }

  inline int width() const { return size[0]; }
  inline int height() const { return size[1]; }
  inline int pixel_area() const { return size[0] * size[1]; }
  inline double aspect() const { return (double)size[0] / (double)size[1]; }

  inline bool in_image(const Eigen::Vector2d & obs) const
  {
    return (obs[0]>=0 && obs[0]<width() && obs[1]>=1 && obs[1]<height());
  }

protected:
  std::array<unsigned,2> size;
};

class LinearCamera : public AbstractCamera
{
public:

  LinearCamera(int w, int h, double fu, double fv, double u0, double v0)
    :AbstractCamera(w,h), _pp(Eigen::Vector2d(u0,v0)), _f(Eigen::Vector2d(fu,fv)),
      _K(Eigen::Matrix3d::Identity()), _Kinv(Eigen::Matrix3d::Identity())
  {
    _K(0,0) = _f[0];
    _K(1,1) = _f[1];
    _K(0,2) = _pp[0];
    _K(1,2) = _pp[1];
    _Kinv(0,0) = 1.0/_f[0];
    _Kinv(1,1) = 1.0/_f[1];
    _Kinv(0,2) = -_pp[0] / _f[0];
    _Kinv(1,2) = -_pp[1] / _f[1];
  }

  inline virtual Eigen::Vector2d map(const Eigen::Vector2d& cam) const
  {
    return Eigen::Vector2d(
      _f[0] * cam[0] + _pp[0],
      _f[1] * cam[1] + _pp[1]
    );
  }

  inline virtual Eigen::Vector2d unmap(const Eigen::Vector2d& img) const
  {
    return Eigen::Vector2d(
      (img[0] - _pp[0]) / _f[0],
      (img[1] - _pp[1]) / _f[1]
    );
  }

  inline const Eigen::Matrix3d& K() const
  {
    return _K;
  }

  inline const Eigen::Matrix3d& Kinv() const
  {
    return _Kinv;
  }

protected:
  Eigen::Vector2d _pp;
  Eigen::Vector2d _f;
  Eigen::Matrix3d _K;
  Eigen::Matrix3d _Kinv;
};

class FovCamera : public LinearCamera
{
public:

  FovCamera(int w, int h, double fu, double fv, double u0, double v0, double W)
      :LinearCamera(w,h,fu,fv,u0,v0), _W(W)
  {
      if( _W != 0.0 )
      {
        _2tan = 2.0 * tan(_W / 2.0 );
        _1over2tan = 1.0 / _2tan;
        _Winv = 1.0 / _W;
      }else{
        _Winv = 0.0;
        _2tan = 0.0;
      }
  }

  inline double rtrans_factor(double r) const
  {
    if(r < 0.001 || _W == 0.0)
      return 1.0;
    else
      return (_Winv* atan(r * _2tan) / r);
  }

  inline double invrtrans(double r) const
  {
    if(_W == 0.0)
      return r;
    return(tan(r * _W) * _1over2tan);
  }

  inline Eigen::Vector2d map(const Eigen::Vector2d& cam) const
  {
    const double fac = rtrans_factor(cam.norm());

    return Eigen::Vector2d(
      fac * _f[0] * cam[0] + _pp[0],
      fac * _f[1] * cam[1] + _pp[1]
    );

  }

  inline Eigen::Vector2d unmap(const Eigen::Vector2d& img) const
  {
      const Eigen::Vector2d distCam = Eigen::Vector2d(
        (img[0] - _pp[0]) / _f[0],
        (img[1] - _pp[1]) / _f[1]
      );
      const double distR = distCam.norm();
      const double R = invrtrans(distR);
      if( distR > 0.01 ) {
          return (R / distR) * distCam;
      }else{
          return distCam;
      }
  }

protected:
  double _W;
  double _Winv;
  double _2tan;
  double _1over2tan;
};

class MatlabCamera : public LinearCamera
{
public:

  MatlabCamera(int w, int h, double fu, double fv, double u0, double v0, double k1, double k2, double p1, double p2, double k3)
      :LinearCamera(w,h,fu,fv,u0,v0), _k1(k1), _k2(k2), _p1(p1), _p2(p2), _k3(k3)
  {
  }

  inline Eigen::Vector2d map(const Eigen::Vector2d& cam) const
  {
      const double rd = cam.norm();
      const double rd2 = rd*rd;
      const double rd4 = rd2*rd2;
      return LinearCamera::map(( (1 + _k1*rd2 + _k2*rd4 + _k3*rd4*rd2)*cam));
  }

  inline Eigen::Vector2d unmap(const Eigen::Vector2d& img) const
  {
      Eigen::Vector2d u = LinearCamera::unmap(img);
      Eigen::Vector2d md = u;

      for (int i=0; i<20; i++)
      {
          double rd = md.norm();
          const double rd2 = rd*rd;
          const double rd4 = rd2*rd2;
          const double radial =  1 + _k1*rd2 + _k2*rd2*rd2 + _k3*rd4*rd2;
          md = u/radial;
      }
      return md;
  }

protected:
  double _k1, _k2, _p1, _p2, _k3;
};

#endif // CAMERA_H
