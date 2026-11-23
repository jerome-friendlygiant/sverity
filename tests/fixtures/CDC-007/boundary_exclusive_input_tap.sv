// sverity-args: --top top --sdc @FIXTURES@/CDC-047/primary_input_exclusive.sdc
// expect: CDC-007 INFO
// expect-none: CDC-007 WARNING
module top(input logic clka, clkb, d, output logic q, tap);
  logic s1, s2; logic u; always_ff @(posedge clka) u <= 1'b0;
  always_ff @(posedge clkb) begin s1 <= d; s2 <= s1; end
  assign q = s2; assign tap = s1;
endmodule
