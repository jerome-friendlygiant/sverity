// sverity-args: --top top
// `rst` is a reset, `rst_q` may be one: `seen` is written only under `rst_q`, so whether it has a reset value cannot be decided. A definite
// INFO here would call it unreset while `rst_q` may reset it.
// expect-unresolved: LNT-067 9
// expect-none: LNT-067 INFO
module top(input logic clk, rst, rst_q, d, output logic seen, y);
  always_ff @(posedge clk) begin
    if (rst) y <= 1'b0; else y <= d;
    if (rst_q) seen <= 1'b1;
  end
endmodule
