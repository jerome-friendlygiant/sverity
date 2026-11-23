// sverity-args: --top top --sdc @DIR@/reset_input_delay.sdc
// Boundary: an active-high async reset port with a valid set_input_delay is exempt from RDC-002.
// expect-none: RDC-002
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or posedge rst_n) if (rst_n) q <= 1'b0; else q <= d;
endmodule
