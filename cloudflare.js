export default {
  async fetch(request, env, ctx) {
    try {
      const url = new URL(request.url);

      // utilzinho p/ padronizar MAC (AA:BB:CC:DD:EE:FF)
      const normalizeMac = (m) => String(m).trim().toUpperCase().replace(/-/g, ":");

      // --- Rota para REGISTRAR a combinação de um MAC com um ID de ESP ---
      if (url.pathname === "/register" && request.method === "POST") {
        let body;
        try {
          body = await request.json();
        } catch (e) {
          return new Response("Erro no JSON recebido: " + e.message, { status: 400 });
        }

        if (!env.ENVIOT) {
          return new Response("ERRO: Binding 'ENVIOT' não encontrado!", { status: 500 });
        }

        const esp_id = String(body.esp_id || "").trim();
        const macInput = body.mac;

        if (!macInput || !esp_id) {
          return new Response("Erro: Os campos 'mac' e 'esp_id' são obrigatórios.", { status: 400 });
        }

        const mac = normalizeMac(macInput); //formata o macinput

        // A chave agora é uma combinação do ID do ESP e do MAC
        const key = `rfid:${esp_id}:${mac}`;

        const existingLink = await env.ENVIOT.get(key);
        if (existingLink) {
          return new Response(
            JSON.stringify({
              message: "Esta combinação de MAC e ESP já está registrada.",
              details: JSON.parse(existingLink),
            }),
            {
              status: 409, // Conflict
              headers: { "Content-Type": "application/json; charset=utf-8" },
            }
          );
        }

        const dataToStore = {
          registeredAt: new Date().toISOString(),
        };

        await env.ENVIOT.put(key, JSON.stringify(dataToStore));

        return new Response(
          JSON.stringify({
            status: "registered",
            esp_id,
            mac,
            ...dataToStore,
          }),
          {
            status: 201, // Created
            headers: { "Content-Type": "application/json; charset=utf-8" },
          }
        );
      }

      // --- Rota para VERIFICAR se um MAC é válido para um ESP específico ---
      if (url.pathname === "/check") {
        const esp_id = String(url.searchParams.get("esp_id") || "").trim();
        const macParam = url.searchParams.get("mac");

        if (!macParam || !esp_id) {
          return new Response("Informe os parâmetros na URL: ?mac=...&esp_id=...", { status: 400 });
        }

        const mac = normalizeMac(macParam);
        const key = `rfid:${esp_id}:${mac}`;
        const data = await env.ENVIOT.get(key, { type: "json" });

        if (!data) {
          return new Response(JSON.stringify({ status: "unauthorized" }), {
            status: 404,
            headers: { "Content-Type": "application/json; charset=utf-8" },
          });
        }

        return new Response(JSON.stringify({ status: "authorized", details: data }), {
          headers: { "Content-Type": "application/json; charset=utf-8" },
        });
      }
      
      // --- Rota para LISTAR todas as combinações registradas ---
      if (url.pathname === "/list") {
        if (!env.ENVIOT) {
          return new Response("ERRO: Binding 'ENVIOT' não encontrado!", { status: 500 });
        }

        const list = await env.ENVIOT.list({ prefix: "rfid:" });
        if (list.keys.length === 0) {
          return new Response(JSON.stringify([]), {
            headers: { "Content-Type": "application/json; charset=utf-8" },
          });
        }

        const promises = list.keys.map((key) => env.ENVIOT.get(key.name, { type: "json" }));
        const values = await Promise.all(promises);

        const result = list.keys.map((key, index) => {
          // Extrai o esp_id e o mac da chave. Ex: "rfid:ESP01:A1:B2:C3:D4"
          const parts = key.name.replace("rfid:", "").split(":");
          const esp = parts.shift(); // primeiro elemento (ESP01)
          const mac = parts.join(":"); // junta o resto (A1:B2:C3:D4)

          return {
            esp_id: esp,
            mac,
            ...values[index],
          };
        });

        return new Response(JSON.stringify(result, null, 2), {
          headers: { "Content-Type": "application/json; charset=utf-8" },
        });
      }

      // --- Resposta Padrão ---
      return new Response("Use POST /register, GET /check?mac=...&esp_id=..., ou GET /list");
    } catch (e) {
      return new Response("Erro inesperado: " + e.message, { status: 500 });
    }
  },
};
