// sverity-args: --top top
// `cnt` is instanced twice: `clr` is the asynchronous reset `arst` in u0 and the data `stop` in u1, so only u0.busy has a reset.
// expect: LNT-054 WARNING 10
module cnt(input logic clk, clr, inc, output logic y);
  logic busy;
  always_ff @(posedge clk) begin
    if (inc) busy <= 1'b1;
    if (clr) busy <= 1'b0;
  end
  assign y = busy ? inc : 1'b0;
endmodule
module top(input logic clk, arst, go, stop, inc, d, output logic y0, y1, q);
  always_ff @(posedge clk or posedge arst) if (arst) q <= 1'b0; else q <= d;
  cnt u0(.clk(clk), .clr(arst), .inc(inc), .y(y0));
  cnt u1(.clk(clk), .clr(stop), .inc(go), .y(y1));
endmodule
