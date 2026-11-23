// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// Boundary: a declared blackbox output, like an undeclared box net, reaching a register remains Unresolved.
// expect: CDC-023 WARNING
// expect-unresolved: CDC-023
// expect-coverage: blackboxes INCOMPLETE
module BLACKBOX(input logic d, output logic q);
endmodule
module top(input logic clka, d, output logic q);
  logic o;
  BLACKBOX u(.d(d), .q(o));
  always_ff @(posedge clka) q <= o;
endmodule
