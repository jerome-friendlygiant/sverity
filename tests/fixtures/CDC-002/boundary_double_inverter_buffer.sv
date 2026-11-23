// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A generic clock buffer inverts twice (OpenTitan prim_clock_buf): `~~clka` is `clka`, the same clock. No generated-clock
// warning, and the register behind the buffer is in clka's domain, so its capture of a clka register is no crossing.
// expect-none: CDC-002
// expect-none: CDC-003
module clock_buf(input logic clk_i, output logic clk_o);
  logic inv;
  assign inv = ~clk_i;
  assign clk_o = ~inv;
endmodule
module top(input logic clka, rst_n, d, output logic q);
  logic clk_buf, launch;
  clock_buf u_buf(.clk_i(clka), .clk_o(clk_buf));
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) launch <= 1'b0; else launch <= d;
  always_ff @(posedge clk_buf or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= launch;
endmodule
