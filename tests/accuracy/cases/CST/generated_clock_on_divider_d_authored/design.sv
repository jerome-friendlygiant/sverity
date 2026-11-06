module clock_divider #(parameter int WIDTH = 3) (
  input  logic clk_i,
  input  logic rst_ni,
  output logic clk_o
);
  logic [WIDTH-1:0] count_q;
  logic count_d;

  always_comb begin
    count_d = ~count_q[0];
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      count_q <= '0;
    else begin
      count_q <= count_q + 1'b1;
      clk_o <= count_q[WIDTH-1];
    end
  end
endmodule

module datapath #(parameter int WIDTH = 8) (
  input  logic slow_clk_i,
  input  logic rst_ni,
  input  logic [WIDTH-1:0] data_i,
  output logic [WIDTH-1:0] data_o
);
  logic [WIDTH-1:0] held_q;
  always_ff @(posedge slow_clk_i or negedge rst_ni) begin
    if (!rst_ni)
      held_q <= '0;
    else
      held_q <= data_i + 1'b1;
  end
  assign data_o = held_q;
endmodule

module top (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic [7:0] data_i,
  output logic [7:0] data_o
);
  logic slow_clk;
  clock_divider u_div (.clk_i, .rst_ni, .clk_o(slow_clk));
  datapath u_path (.slow_clk_i(slow_clk), .rst_ni, .data_i, .data_o);
endmodule
