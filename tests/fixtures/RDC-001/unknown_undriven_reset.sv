// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The reset net rst_n is never driven, so its source is Undriven: the analysis cannot decide the reset, so the WARNING stays Unresolved (its INFO companion is the source report) and the scope is INCOMPLETE.
// expect: RDC-001 WARNING
// expect-count: RDC-001 WARNING 1
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, d, output logic q);
  logic rst_n;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
