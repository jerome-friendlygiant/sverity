// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// One reset net clears one register and sets another: ordinary design (reset values 0 and 1), not a finding of this rule
// (the cleared/set register counts are evidence on the RDC-001 inventory).
// expect-none: RDC-019 WARNING
// expect: RDC-001 INFO
module top(input logic clka, rst_n, d, output logic q, r);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r <= 1'b1; else r <= d;
endmodule
