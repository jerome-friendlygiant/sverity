// expect: LNT-062 WARNING 3
module top(input logic d, output logic q);
  task update; q <= d; endtask
  always_comb update();
endmodule
