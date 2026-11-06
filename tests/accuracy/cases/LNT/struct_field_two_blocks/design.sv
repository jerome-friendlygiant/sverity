typedef struct packed {
  logic       valid;
  logic       fault;
  logic [3:0] code;
} status_t;

module status_builder (
  input  logic [7:0] payload,
  input  logic       ready,
  input  logic       parity_bad,
  output status_t    status
);
  function automatic logic [3:0] make_code(input logic [7:0] value);
    make_code = value[7:4] ^ value[3:0];
  endfunction

  always_comb begin
    status.valid = ready;
    status.fault = parity_bad;
    status.code  = make_code(payload);
  end

  always_comb begin
    if (payload == 8'h00)
      status.fault = 1'b1;
    else
      status.fault = parity_bad;
  end
endmodule

module top (
  input  logic [7:0] payload,
  input  logic       ready,
  input  logic       parity_bad,
  output logic       valid,
  output logic       fault,
  output logic [3:0] code
);
  status_t status;

  status_builder u_builder (
    .payload(payload),
    .ready(ready),
    .parity_bad(parity_bad),
    .status(status)
  );

  assign valid = status.valid;
  assign fault = status.fault;
  assign code  = status.code;
endmodule
