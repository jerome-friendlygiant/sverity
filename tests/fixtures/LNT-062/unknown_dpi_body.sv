// expect: LNT-062 WARNING 4
module top;
  import "DPI-C" function void external_update();
  always_comb external_update();
endmodule
