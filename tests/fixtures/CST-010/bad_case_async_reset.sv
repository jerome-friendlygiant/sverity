// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The asynchronous reset is tested by a case statement: the value it forces is not analysed, so the model reports a precision fallback.
// revert: always_ff @(posedge clka or negedge rst_n) case (rst_n) 1'b0: q <= 1'b0; default: q <= d; endcase => always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
// revert-expect-none: CST-010 WARNING
// expect-unresolved: CST-010
// expect-coverage: graph-precision INCOMPLETE
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) case (rst_n) 1'b0: q <= 1'b0; default: q <= d; endcase
endmodule
