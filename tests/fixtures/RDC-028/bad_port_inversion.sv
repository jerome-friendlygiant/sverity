// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// the child's reset is the inverted actual ~rst_n; the polarity change through a port connection is reported like an assign
// expect: RDC-028 INFO
module child(input logic clk, rst, d, output logic q);
  always_ff @(posedge clk or posedge rst) if (rst) q <= 1'b0; else q <= d;
endmodule
module top(input logic clka, rst_n, d, output logic q);
  child u(.clk(clka), .rst(~rst_n), .d(d), .q(q));
endmodule
