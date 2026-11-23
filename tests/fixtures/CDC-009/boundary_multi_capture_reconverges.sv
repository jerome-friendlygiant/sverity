// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Each source bit is synchronized in two independent capture groups, then the copies reconverge.
// expect: CDC-009 WARNING
// expect-confidence: CDC-009 established
module top(input logic clka, clkb, d0, d1, output logic [1:0] q);
  logic [1:0] a_q;
  logic d0_s1, d0_s2, d0_t1, d0_t2;
  logic d1_s1, d1_s2, d1_t1, d1_t2;
  always_ff @(posedge clka) a_q <= {d1, d0};
  always_ff @(posedge clkb) begin
    d0_s1 <= a_q[0]; d0_s2 <= d0_s1;
    d0_t1 <= a_q[0]; d0_t2 <= d0_t1;
    d1_s1 <= a_q[1]; d1_s2 <= d1_s1;
    d1_t1 <= a_q[1]; d1_t2 <= d1_t1;
  end
  assign q = {d1_s2 | d1_t2, d0_s2 | d0_t2};
endmodule
