// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A reset input whose async uses are synchroniser stages only, but which also has a functional use (synchronous reset or data), must still report RDC-002 WARNING.
// revert: if (!por_n) p <= 1'b0; => if (!sr[1]) p <= 1'b0;
// revert-expect-none: RDC-002 WARNING
// expect: RDC-002 WARNING
// expect-unresolved: RDC-002
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, por_n, d, output logic q, p);
  logic [1:0] sr;
  always_ff @(posedge clka or negedge por_n)
    if (!por_n) sr <= 2'b00;
    else sr <= {sr[0], 1'b1};
  always_ff @(posedge clka or negedge sr[1])
    if (!sr[1]) q <= 1'b0;
    else q <= d;
  always_ff @(posedge clka)
    if (!por_n) p <= 1'b0;
    else p <= d;
endmodule
