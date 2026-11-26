// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An async reset sampled as data only by the first stage of a recognised 2-stage CDC data synchroniser is exempt from RDC-019.
// expect-none: RDC-019
module top(input logic clka, clkb, rst_a_n, rst_b_n, d, output logic q_a, rst_seen_b);
  logic s1_q, s2_q;
  always_ff @(posedge clka or negedge rst_a_n)
    if (!rst_a_n) q_a <= 1'b0;
    else q_a <= d;
  always_ff @(posedge clkb or negedge rst_b_n)
    if (!rst_b_n) begin s1_q <= 1'b0; s2_q <= 1'b0; end
    else begin s1_q <= rst_a_n; s2_q <= s1_q; end
  assign rst_seen_b = s2_q;
endmodule
