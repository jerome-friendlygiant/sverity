// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-041
module top(input logic clka, clkb, rst_a_n, rst_b_n, input logic [1:0] d, output logic [1:0] q);
  logic [1:0] a_q, s1, s2;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) a_q[0] <= 0; else a_q[0] <= d[0];
  always_ff @(posedge clka or negedge rst_b_n) if (!rst_b_n) a_q[1] <= 0; else a_q[1] <= d[1];
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) begin s1[0] <= 0; s2[0] <= 0; end else begin s1[0] <= a_q[0]; s2[0] <= s1[0]; end
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) begin s1[1] <= 0; s2[1] <= 0; end else begin s1[1] <= a_q[1]; s2[1] <= s1[1]; end
  assign q = s2;
endmodule
