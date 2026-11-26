// sverity-args: --top top --sdc @DIR@/input_delay_other.sdc
// A top-level reset port with set_input_delay on a different (asynchronous) clock is not timed to the receivers' clock and must stay RDC-003 WARNING.
// expect: RDC-003 WARNING
// expect-unresolved: RDC-003
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, clkb, por_n, d, output logic q, p);
  always_ff @(posedge clka or negedge por_n)
    if (!por_n) q <= 1'b0;
    else q <= d;
  always_ff @(posedge clkb) p <= d;
endmodule
