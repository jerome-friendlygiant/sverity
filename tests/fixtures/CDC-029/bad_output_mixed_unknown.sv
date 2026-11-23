// sverity-args: --top top --sdc @DIR@/output_mixed_unknown.sdc
// expect: CDC-029 ERROR
// expect-confidence: CDC-029 established
// expect-coverage: constraints INCOMPLETE
module top(input logic launch_a, capture_b, unrelated_c, data, output logic result);
  assign result = data;
endmodule
