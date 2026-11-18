// sverity-args: --top top
// Lookalike of good_read_under_flag_set_by_the_load: the flag has no reset, only a clear under the data signal `other`,
// so it holds no known value after power-up and is itself read before it is defined.
// expect: LNT-054 WARNING 14
// expect: LNT-054 WARNING 14
module top(input logic clk, rst_n, ack, other, input logic [3:0] d, output logic y);
  logic [3:0] word_q;
  logic seen_q;
  always_ff @(posedge clk) if (ack) word_q <= d;
  always_ff @(posedge clk) begin
    if (other) seen_q <= 1'b0;
    else if (ack) seen_q <= 1'b1;
  end
  assign y = seen_q && word_q == d ? 1'b1 : 1'b0;
endmodule
