// sverity-args: --top top --sdc @FIXTURES@/CDC-016/unknown_relation.sdc --library @DIR@/cells.json
// A clean read chain cannot make an unconstrained clock relation safe.
// expect-unresolved: CDC-035
// expect-coverage: clock-relationships INCOMPLETE
module DPRAM(input logic WCLK, RCLK, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, clkb, d, output logic q);
  logic rd, s1, s2;
  DPRAM u_ram(.WCLK(clka), .RCLK(clka), .WD(d), .RD(rd));
  always_ff @(posedge clkb) begin s1 <= rd; s2 <= s1; end
  assign q = s2;
endmodule
