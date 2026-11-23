// sverity-args: --top top --library @DIR@/cells.json
// A constant-zero RAM clock is not a clock domain and leaves CDC-035 unresolved.
// expect: CDC-035 WARNING
// expect-unresolved: CDC-035
// expect-coverage: clock-extraction INCOMPLETE
module DPRAM(input logic WCLK, RCLK, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, d, output logic rd);
  DPRAM u_ram(.WCLK(clka), .RCLK(1'b0), .WD(d), .RD(rd));
endmodule
