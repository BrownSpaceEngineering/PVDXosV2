### 
```
$ cd libscp-2.0 

$ python3 waf configure \
  --toolchain=arm-none-eabi- \
  --with-os=freertos \
  --includes=../asf/thirdparty/rtos/freertos/freertosv10.0.0/source/include,../asf/thirdparty/rtos/freertos/freertosv10.0.0/source/portable/gcc/arm_cm4f,../ASF/config/,injected_headers/ \
  --prefix=install

$ python3 waf build install
```
