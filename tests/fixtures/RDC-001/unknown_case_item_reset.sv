// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The reset is tested only by a case item (`case (1'b1) !rst_n:`), so the value it forces is not analysed: the reset stays listed,
// reset extraction is INCOMPLETE, and the register is not called unreset (RDC-025).
// revert: always_ff @(posedge clka or negedge rst_n) case (1'b1) !rst_n: q <= 0; default: q <= d; endcase => always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 0; else q <= d;
// revert-expect-none: RDC-025 WARNING
// expect: RDC-001 INFO
// expect-coverage: reset-extraction INCOMPLETE
// expect-unresolved: RDC-025
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) case (1'b1) !rst_n: q <= 0; default: q <= d; endcase
endmodule
