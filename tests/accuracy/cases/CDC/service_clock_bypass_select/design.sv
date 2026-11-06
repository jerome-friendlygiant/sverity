module top (
  input logic run_clk,
  input logic service_clk,
  input logic service_mode,
  input logic [7:0] command,
  output logic [7:0] command_q,
  output logic parity_q
);
  wire selected_clk;
  // Service mode is controlled by the system controller.
  assign selected_clk = service_mode ? service_clk : run_clk;
  always_ff @(posedge selected_clk) begin
    command_q <= command;
    parity_q <= ^command;
  end
endmodule
