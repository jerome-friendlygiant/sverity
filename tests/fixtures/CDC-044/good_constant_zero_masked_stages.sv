// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Constant-zero masked or self-canceling stage expressions do not observe adjacent stages.
// expect-none: CDC-044
module top(input logic clka, rx, output logic hit_a, hit_b);
  logic s1, s2, s3;
  always_ff @(posedge clka) begin
    s1 <= rx;
    s2 <= s1;
    s3 <= s2;
  end
  assign hit_a = 1'b0 & (s2 ^ s3);
  assign hit_b = (s2 ^ s3) & 1'b0;
endmodule
