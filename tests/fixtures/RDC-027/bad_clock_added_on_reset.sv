// sverity-args: --top top --sdc @DIR@/clock_two.sdc
// create_clock -add gives the reset port rst_n[3] a second clock, `second`; the exception selects `second`, so it covers this reset.
// expect: RDC-027 WARNING
module top(input logic clka, d, input logic [5:2] rst_n, output logic q);
  always_ff @(posedge clka or negedge rst_n[3]) if (!rst_n[3]) q <= 1'b0; else q <= d;
endmodule
