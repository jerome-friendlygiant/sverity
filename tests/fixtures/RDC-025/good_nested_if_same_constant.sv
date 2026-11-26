// sverity-args: --top top
// Nested if/else and case inside an async reset branch whose every arm forces the same constant to every bit are decided.
// expect-none: RDC-010
// expect-none: RDC-020
// expect-none: RDC-025
module top(input logic clk, rst_n, mode, input logic [1:0] sel, d, output logic [1:0] q_if, q_case);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      if (mode) q_if <= 2'b00;
      else q_if <= 2'b00;
      case (sel)
        2'd0: q_case <= 2'b10;
        2'd1: q_case <= 2'b10;
        default: q_case <= 2'b10;
      endcase
    end else begin
      q_if <= d;
      q_case <= d;
    end
endmodule
