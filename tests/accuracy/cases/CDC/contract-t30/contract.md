# CDC-RFC1-T30 interface contract

`macro_clk` is 12 ns and `dst_clk` is 14 ns; SDC declares them asynchronous. The macro's output timing, reset behavior, alarm protocol and event retention are unavailable. The IP reset only resets the destination synchronizer and consumer; it does not reset the macro.

