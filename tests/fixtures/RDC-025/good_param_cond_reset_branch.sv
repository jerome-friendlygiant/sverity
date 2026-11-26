// sverity-args: --top top
// A parameter/localparam condition inside an async reset branch folds using its elaborated value, so the forced constant is decided.
// expect-none: RDC-010
// expect-none: RDC-020
// expect-none: RDC-025
module top #(parameter bit INIT_HI = 1'b0)(input logic clk, rst_n, input logic [1:0] d, output logic [1:0] q);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      if (INIT_HI) q <= 2'b11;
      else q <= 2'b00;
    end else begin
      q <= d;
    end
endmodule
