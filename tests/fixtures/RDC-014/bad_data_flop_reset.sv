// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_ff @(posedge clkb or negedge strobe) if (!strobe) q => always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q
// revert-expect-none: RDC-014 WARNING
// expect: RDC-014 WARNING
module top(input logic clka, clkb, rst_n, req, d, output logic q);
  logic strobe;
  always_ff @(posedge clka) strobe <= req;
  always_ff @(posedge clkb or negedge strobe) if (!strobe) q <= 1'b0; else q <= d;
endmodule
