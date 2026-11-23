// sverity-args: --top top --sdc @DIR@/direct_capture_async.sdc
// The top-level input has an explicit asynchronous launch contract but no synchronizer.
// expect: CDC-029 ERROR
// expect-confidence: CDC-029 established
module top(input logic clka, clkb, rst_n, din, output logic q);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= din;
endmodule
