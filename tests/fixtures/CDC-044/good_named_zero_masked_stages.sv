// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Named constant-zero wire masking an adjacent-stage expression does not observe adjacent stages.
// expect-none: CDC-044
module top(input logic clka, rx, output logic hit);
  logic s1, s2, s3;
  always_ff @(posedge clka) begin
    s1 <= rx;
    s2 <= s1;
    s3 <= s2;
  end
  wire zer = 1'b0;
  assign hit = (s2 ^ s3) & zer;
endmodule
