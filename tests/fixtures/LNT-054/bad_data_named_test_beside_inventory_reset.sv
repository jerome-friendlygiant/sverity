// sverity-args: --top top
// `arst_n` is an asynchronous reset elsewhere and resets `q`; `rst_done` is a status flag, so `p` has no reset.
// expect: LNT-054 WARNING 11
module top(input logic clk, arst_n, rst_done, d, output logic y);
  logic p, q, r;
  always_ff @(posedge clk or negedge arst_n) if (!arst_n) r <= 1'b0; else r <= d;
  always_ff @(posedge clk) begin
    if (rst_done) p <= 1'b0;
    if (!arst_n) q <= 1'b0; else q <= d;
  end
  assign y = p ? (q & r) : 1'b0;
endmodule
