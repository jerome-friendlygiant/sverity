// sverity-args: --top top --sdc @DIR@/input_delay_clka.sdc
// Reset input rsta has set_input_delay on clka: its synchronous reset use in clka is covered by the input-delay trust model (no RDC-019).
// expect-none: RDC-019
module top(input logic clka, clkb, rsta, da, output logic qa, qf);
  always_ff @(posedge clka or posedge rsta) if (rsta) qf <= 1'b1; else qf <= 1'b0;
  always_ff @(posedge clka) if (rsta) qa <= 1'b0; else qa <= da;
endmodule
