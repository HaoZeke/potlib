SoftGilRelease in the metatomic C ABI only calls PyEval_SaveThread when PyGILState_Check is true, so nested GIL release from pyeonclient Job.run is safe.
