module command_merge #(
  parameter int SOURCES = 3,
  parameter int WIDTH = 8
)(
  input logic [SOURCES-1:0] select,
  input logic [SOURCES-1:0][WIDTH-1:0] commands,
  output logic [WIDTH-1:0] command
);
  always_comb begin
    command = '0;
    for (int k = 0; k < SOURCES; k++)
      command |= commands[k] & {WIDTH{select[k]}};
  end
endmodule

module top (
  input logic [2:0] select,
  input logic [2:0][7:0] commands,
  output logic [7:0] command
);
  // Requesters are provided by the surrounding interconnect.
  command_merge merge_i (.select(select), .commands(commands), .command(command));
endmodule
