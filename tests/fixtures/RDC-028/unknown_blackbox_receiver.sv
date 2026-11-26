// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --ignore-unknown-modules
// Looks undecidable: rst is the inverted copy of rst_n, drives the async reset of r and also feeds sink, an undeclared black box whose reset polarity is never read. The rule still decides the root: rst is logic-built from one top input through an inverting assign, so the INFO (Heuristic) holds for r, and the black-box gap is reported as INCOMPLETE.
// expect: RDC-028 INFO
// expect-coverage: blackboxes INCOMPLETE
module top(input logic clka, rst_n, d, output logic q, r);
  logic rst;
  assign rst = ~rst_n;
  always_ff @(posedge clka or posedge rst) if (rst) r <= 1'b0; else r <= d;
  sink u (.clk(clka), .rst(rst), .d(d), .q(q));
endmodule
