// sverity-args: --top top --sdc @DIR@/input_delay_clka.sdc
// Reset input rsta has set_input_delay on clka, but is also used synchronously in async domain clkb: RDC-019 must still warn.
// expect: RDC-019 WARNING
module top(input logic clka, clkb, rsta, db, output logic qb, qf);
  always_ff @(posedge clka or posedge rsta) if (rsta) qf <= 1'b1; else qf <= 1'b0;
  always_ff @(posedge clkb) if (rsta) qb <= 1'b0; else qb <= db;
endmodule
