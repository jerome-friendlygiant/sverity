// Status and shadow register pair, written in the legacy non-ANSI port style.
module top (clk, rst_n, we, wdata, status, shadow);
  input        clk;
  input        rst_n;
  input        we;
  input  [7:0] wdata;
  output [7:0] status;
  output [7:0] shadow;

  reg [7:0] status;
  reg [7:0] shadow;
  wire      load;

  assign load = we & ~(&wdata);

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      status <= 8'h00;
      shadow <= 8'h00;
    end else if (load) begin
      status <= wdata;
      shadow <= status;
    end
  end
endmodule
