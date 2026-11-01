// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// bit 0 is cleared by rst_a_n and set by rst_b_n; only bit 1 would agree. Judged per bit, not per first bit.
// expect: RDC-026 WARNING
module top(input logic clka, rst_a_n, rst_b_n, input logic [1:0] d, output logic [1:0] q);
  always_ff @(posedge clka or negedge rst_a_n or negedge rst_b_n)
    if (!rst_a_n) q <= 2'b00; else if (!rst_b_n) q <= 2'b01; else q <= d;
endmodule
