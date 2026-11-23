// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An intermediate net with competing drivers is not an exact alias of a synchronizer stage.
// expect-none: CDC-044
module top(input logic clka, rx, en, output wire [1:0] word);
  logic s1, s2, s3;
  always_ff @(posedge clka) begin
    s1 <= rx;
    s2 <= s1;
    s3 <= s2;
  end
  wire ali;
  assign ali = s2;
  assign ali = en;
  assign word[0] = ali;
  assign word[1] = s3;
endmodule
