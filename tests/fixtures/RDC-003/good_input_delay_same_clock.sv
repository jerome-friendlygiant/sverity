// sverity-args: --top top --sdc @DIR@/input_delay_same.sdc
// A top-level reset port with an applied, unrejected set_input_delay on the receivers' clock domain reports INFO (trust model), not WARNING.
// expect: RDC-003 INFO
// expect-none: RDC-003 WARNING
module top(input logic clka, por_n, d, output logic q);
  always_ff @(posedge clka or negedge por_n)
    if (!por_n) q <= 1'b0;
    else q <= d;
endmodule
