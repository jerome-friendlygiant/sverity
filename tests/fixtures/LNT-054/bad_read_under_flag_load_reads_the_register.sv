// sverity-args: --top top
// Lookalike of good_read_under_flag_set_by_the_load: the load stores `word_q + 1`, so the register is defined only if it already is
// and the flag proves nothing about its value.
// revert: word_q <= word_q + 4'd1 => word_q <= d
// revert-expect-none: LNT-054
// expect: LNT-054 WARNING 15
module top(input logic clk, rst_n, ack, other, input logic [3:0] d, output logic y);
  logic [3:0] word_q;
  logic seen_q;
  always_ff @(posedge clk) if (ack) word_q <= word_q + 4'd1;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) seen_q <= 1'b0;
    else if (ack) seen_q <= 1'b1;
  end
  assign y = seen_q && word_q == d ? 1'b1 : 1'b0;
endmodule
