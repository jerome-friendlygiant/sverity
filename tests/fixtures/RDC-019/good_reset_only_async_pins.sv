// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_reset_as_data.sv: y takes d alone, so rst_n only drives asynchronous reset pins.
// expect-none: RDC-019
module top(input logic clka, rst_n, d, output logic q, y);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clka) y <= d;
endmodule
