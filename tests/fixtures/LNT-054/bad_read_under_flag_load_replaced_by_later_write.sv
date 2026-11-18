// sverity-args: --top top
// Lookalike of good_read_under_flag_set_by_the_load: a later write of the same procedure replaces the loaded value
// (here with a shift of the old contents) whenever `other` is set, even when `ack` is.
// revert: if (other) word_q <= {word_q[2:0], d[0]}; => if (other) begin end
// revert-expect-none: LNT-054
// expect: LNT-054 WARNING 18
module top(input logic clk, rst_n, ack, other, input logic [3:0] d, output logic y);
  logic [3:0] word_q;
  logic seen_q;
  always_ff @(posedge clk) begin
    if (ack) word_q <= d;
    if (other) word_q <= {word_q[2:0], d[0]};
  end
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) seen_q <= 1'b0;
    else if (ack) seen_q <= 1'b1;
  end
  assign y = seen_q && word_q == d ? 1'b1 : 1'b0;
endmodule
