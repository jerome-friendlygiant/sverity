// sverity-args: --top top --sdc @DIR@/input_delay_through.sdc
// A reset input covered by set_false_path -through is not timed by its set_input_delay: RDC-019 WARNING and RDC-003 WARNING must fire.
// expect: RDC-019 WARNING
// expect: RDC-003 WARNING
// expect-none: RDC-003 INFO
module top(input logic clka, clkb, rsta, da, output logic qf, qa);
  always_ff @(posedge clka or posedge rsta) if (rsta) qf <= 1'b1; else qf <= 1'b0;
  always_ff @(posedge clka) if (rsta) qa <= 1'b0; else qa <= da;
endmodule
