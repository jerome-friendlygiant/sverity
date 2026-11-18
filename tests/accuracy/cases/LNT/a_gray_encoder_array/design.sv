// Registered Gray encoder for several independent counter lanes.
module top #(
  parameter int LANES = 4,
  parameter int W     = 6
) (
  input  logic               clk,
  input  logic               rst_n,
  input  logic [LANES*W-1:0] bin_i,
  output logic [LANES*W-1:0] gray_o
);
  for (genvar l = 0; l < LANES; l++) begin : g_lane
    logic [W-1:0] bin_l;
    logic [W-1:0] gray_q;

    assign bin_l = bin_i[l*W +: W];

    always_ff @(posedge clk or negedge rst_n) begin
      if (!rst_n) gray_q <= '0;
      else        gray_q <= to_gray(bin_l);
    end

    assign gray_o[l*W +: W] = gray_q;
  end

  function automatic logic [W-1:0] to_gray(input logic [W-1:0] b);
    return b ^ {1'b0, b[W-1:1]};
  endfunction
endmodule
