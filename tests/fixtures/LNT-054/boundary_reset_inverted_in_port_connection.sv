// sverity-args: --top top
// An inversion in the port connection (`!rst_ni`, `~rst_ni`) keeps the reset a reset, with the opposite polarity; the same inversion of
// data (`!stop`) does not make one, so `busy` of that instance has no reset.
// expect-count: LNT-054 WARNING 1
// expect: LNT-054 WARNING 12
module cnt(input logic clk, clr, inc, output logic y);
  logic busy;
  always_ff @(posedge clk) begin
    if (inc) busy <= 1'b1;
    if (clr) busy <= 1'b0;
  end
  assign y = busy ? inc : 1'b0;
endmodule
module top(input logic clk, rst_ni, stop, inc, d, output logic y0, y1, y2, q);
  always_ff @(posedge clk or negedge rst_ni) if (!rst_ni) q <= 1'b0; else q <= d;
  cnt u0(.clk(clk), .clr(!rst_ni), .inc(inc), .y(y0));
  cnt u1(.clk(clk), .clr(~rst_ni), .inc(inc), .y(y1));
  cnt u2(.clk(clk), .clr(!stop), .inc(inc), .y(y2));
endmodule
