// sverity-args: --top top --sdc @DIR@/fp.sdc
// The exception names rst_n, but rst_n is only a synchronous control and is not an asynchronous reset root.
// expect-none: RDC-027
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
