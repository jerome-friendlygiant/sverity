// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-001 INFO
// expect-none: CDC-001 WARNING
// expect-coverage: clock-extraction COMPLETE
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
