// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Unknown: rst_n comes from a module whose output is never driven, so its recovery/removal timing and glitch behaviour cannot be seen; the finding is reported as an obligation, never as safe, and the reset-extraction scope is INCOMPLETE.
// expect: RDC-013 WARNING
// expect-coverage: reset-extraction INCOMPLETE
module ext_rst(output logic rst_n);
endmodule
module top(input logic clka, d, output logic q);
  logic rst_n;
  ext_rst u_rst(.rst_n(rst_n));
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
