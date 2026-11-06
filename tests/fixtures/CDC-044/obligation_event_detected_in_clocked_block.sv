// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: pulse <= s2 ^ s3; => pulse <= s2;
// revert-expect-none: CDC-044 WARNING
// The edge detector is written in the clocked block itself: the input is still an event stream with unproved spacing.
// expect: CDC-044 WARNING
module top(input logic clka, toggle_in, output logic pulse);
  logic s1, s2, s3;
  always_ff @(posedge clka) begin
    s1 <= toggle_in;
    s2 <= s1;
    s3 <= s2;
    pulse <= s2 ^ s3;
  end
endmodule
