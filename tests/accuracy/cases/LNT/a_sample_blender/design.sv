// Averages two registered 8-bit samples (previous and current).
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       en,
  input  logic [7:0] cur,
  output logic [7:0] avg
);
  logic [7:0] prev_q;
  logic [7:0] avg_q;
  logic [8:0] sum9;

  assign sum9 = {1'b0, prev_q} + {1'b0, cur};

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      prev_q <= 8'h00;
      avg_q  <= 8'h00;
    end else if (en) begin
      prev_q <= cur;
      avg_q  <= sum9[8:1];
    end
  end

  assign avg = avg_q;
endmodule
