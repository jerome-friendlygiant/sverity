// sverity-args: --top top --sdc @DIR@/reset_exception.sdc
// A timing exception (set_false_path) alone supplies no reset contract and must not silence RDC-002.
// expect: RDC-002 WARNING
// expect-unresolved: RDC-002
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
