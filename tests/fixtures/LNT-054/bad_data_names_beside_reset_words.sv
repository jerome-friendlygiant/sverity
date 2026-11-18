// sverity-args: --top top
// A reset word beside a status or request word (`reset_pending`, `do_reset`) or no reset word at all (`first_beat`) is data: none of
// them resets `busy`.
// expect: LNT-054 WARNING 14
module top(input logic clk, start, reset_pending, do_reset, first_beat, d, output logic y);
  logic busy;
  always_ff @(posedge clk) begin
    if (start) busy <= 1'b1;
    if (reset_pending) busy <= 1'b0;
    if (do_reset) busy <= 1'b0;
    if (first_beat) busy <= 1'b0;
  end
  always_comb begin
    if (busy) y = d; else y = 1'b0;
  end
endmodule
