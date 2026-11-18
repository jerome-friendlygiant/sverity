// sverity-args: --top top
// `s` is written only under `rst_q` and `rst_x`, neither of which is clearly a reset. It is reset-only if both are resets, and loads if
// either is data: each test read alone says "loads", so the two read together decide. Unresolved, never silence.
// expect-unresolved: LNT-020 7
module top(input logic clk, rst_q, rst_x, output logic s);
  always_ff @(posedge clk) begin
    if (rst_q) s <= 1'b0;
    if (rst_x) s <= 1'b1;
  end
endmodule
