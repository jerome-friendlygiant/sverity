// sverity-args: --top top
// Lookalike of good_read_under_flag_set_by_the_load: both conditions read the blocking temporary `t`, which `other` replaces between the
// load and the flag write, so they are not the same condition.
// expect: LNT-054 WARNING 19
module top(input logic clk, rst_n, ack, other, input logic [3:0] d, output logic y);
  logic [3:0] word_q;
  logic seen_q, t;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      seen_q <= 1'b0;
      t = 1'b0;
    end else begin
      t = ack;
      if (t) word_q <= d;
      t = other;
      if (t) seen_q <= 1'b1;
    end
  end
  assign y = seen_q && word_q == d ? 1'b1 : 1'b0;
endmodule
